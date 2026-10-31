/*
 * n3ds_debug.c -- the 3D checker and the live tuning page (Nintendo 3DS only).
 *
 * Opened in a match with L + SELECT. The match is frozen on the frame that was showing (the
 * game waits inside N3DS_Present), and that frame stays live: it is composed again every
 * pass, so a change to the depth settings shows at once, and the cursor, marks and zoom are
 * drawn over the two finished eyes.
 *
 *   top screen     the frozen frame in 3D, zoomable, with a cursor that has a DEPTH of its own:
 *                  push it in or out until it sits on the thing you are looking at
 *   bottom screen  what is under the cursor (which part of the model drew it and the depth it
 *                  was given), your wanted depth, the mode and the keys
 *   SWOS.log       a "[3dcheck]" line for every mark: position, world position, the part of the
 *                  model, the depth now and the depth you wanted
 *
 *   modes (X)   POINT  a sample          PIXEL  zoomed, one source pixel, and its neighbours
 *               LINE   two ends          TUNE   the stereo.ini values, edited live; A saves
 *
 *   circle pad / D-pad left-right: move the cursor      D-pad up-down: its depth (1/8 pixel)
 *   L / R: zoom out / in      A: mark      Y: clear marks      SELECT: wanted depth := surface
 *   B or START: close and carry on with the match
 */
#ifdef __3DS__

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "n3ds.h"
#include "stereo.h"

#define TOP_W 400
#define TOP_H 240
#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

enum { M_POINT, M_PIXEL, M_LINE, M_TUNE, M_COUNT };
static const char *const k_modeName[M_COUNT] = { "POINT", "PIXEL", "LINE", "TUNE" };
static const char *const k_kindName[] = { "PITCH", "STANDS", "HOARDING", "SPRITE", "HUD", "OVERLAY" };
static const int32_t k_zoom[] = { 1, 2, 3, 4, 6, 8 };

typedef struct { int32_t x, y, now, want, kind; } Mark;

static int32_t s_active, s_mode, s_zoomI, s_cx = 200, s_cy = 120, s_want, s_manual;
static int32_t s_tuneSel, s_numMarks, s_lineN;
static Mark    s_marks[32], s_lineA, s_lineB;
static char    s_msg[48];
static int32_t s_msgTicks;
static const uint8_t *s_fb;
static int32_t s_w, s_h, s_strength;
static StereoProbe s_pr;
static int32_t s_prOk;
static int32_t s_rep[6], s_accX, s_accY;

static int32_t zoom_now(void) { return k_zoom[s_zoomI]; }

static void say(const char *m)
{
    snprintf(s_msg, sizeof s_msg, "%s", m);
    s_msgTicks = 90;
}

static int32_t src_x(int32_t ox) { return ox * s_w / TOP_W; }
static int32_t src_y(int32_t oy) { return oy * s_h / TOP_H; }

static void probe_at(int32_t ox, int32_t oy, StereoProbe *p, int32_t *ok)
{
    *ok = Stereo_Probe(s_fb, s_w, src_x(ox), src_y(oy), p);
}

static void log_params(const char *tag)
{
    int32_t n, i;
    const StereoParam *p = Stereo_Params(&n);
    fprintf(stderr, "[3dcheck] %s", tag);
    for (i = 0; i < n; i++)
        fprintf(stderr, " %s=%d", p[i].name, (int)*p[i].value);
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* ---- opening and closing ------------------------------------------------------ */

void N3DSDbg_Open(void)
{
    s_active = 1;
    s_numMarks = 0;
    s_lineN = 0;
    s_manual = 0;
    s_msgTicks = 0;
    memset(s_rep, 0, sizeof s_rep);
    log_params("OPEN");
}

int32_t N3DSDbg_Active(void) { return s_active; }

void N3DSDbg_Frame(const uint8_t *fb, int32_t w, int32_t h, int32_t strength)
{
    s_fb = fb;
    s_w = w;
    s_h = h;
    s_strength = strength;
}

/* ---- input -------------------------------------------------------------------- */

/* 1 on the press, then again after a pause, then every other frame while held. */
static int32_t repeat(uint32_t held, uint32_t key, int32_t slot)
{
    if (!(held & key)) { s_rep[slot] = 0; return 0; }
    s_rep[slot]++;
    return s_rep[slot] == 1 || (s_rep[slot] > 14 && (s_rep[slot] & 1) == 0);
}

static void clamp_cursor(void)
{
    if (s_cx < 0) s_cx = 0;
    if (s_cx > TOP_W - 1) s_cx = TOP_W - 1;
    if (s_cy < 0) s_cy = 0;
    if (s_cy > TOP_H - 1) s_cy = TOP_H - 1;
}

static void log_mark(const char *what, const Mark *m, const StereoProbe *p)
{
    fprintf(stderr, "[3dcheck] %s out=%d,%d src=%d,%d world=%d,%d kind=%s now=%d want=%d idx=%d bg=%d "
                    "strength=%d zoom=%d\n",
            what, (int)m->x, (int)m->y, (int)src_x(m->x), (int)src_y(m->y), (int)p->wx, (int)p->wy,
            k_kindName[m->kind], (int)m->now, (int)m->want, (int)p->idx, (int)p->bgIdx,
            (int)s_strength, (int)zoom_now());
    fflush(stderr);
}

static void do_mark(void)
{
    Mark m;
    m.x = s_cx; m.y = s_cy; m.now = s_pr.depth; m.want = s_want; m.kind = s_pr.kind;
    if (!s_prOk) { say("NOTHING HERE"); return; }
    if (s_mode == M_LINE) {
        if (s_lineN == 0 || s_lineN == 2) {
            s_lineA = m;
            s_lineN = 1;
            say("LINE: FIRST END SET");
        } else {
            s_lineB = m;
            s_lineN = 2;
            {
                StereoProbe pa;
                int32_t ok;
                probe_at(s_lineA.x, s_lineA.y, &pa, &ok);
                log_mark("LINE A", &s_lineA, &pa);
                log_mark("LINE B", &s_lineB, &s_pr);
            }
            say("LINE LOGGED");
        }
        return;
    }
    if (s_mode == M_PIXEL) {
        /* the pixel and its eight neighbours: palette indices, row by row */
        int32_t dx, dy;
        fprintf(stderr, "[3dcheck] PIXEL neighbours (palette index, rows top to bottom):");
        for (dy = -1; dy <= 1; dy++) {
            fprintf(stderr, " |");
            for (dx = -1; dx <= 1; dx++) {
                StereoProbe q;
                int32_t ok;
                probe_at(s_cx + dx, s_cy + dy, &q, &ok);
                fprintf(stderr, " %d", ok ? (int)q.idx : -1);
            }
        }
        fprintf(stderr, "\n");
    }
    if (s_numMarks < 32)
        s_marks[s_numMarks++] = m;
    log_mark(s_mode == M_PIXEL ? "PIXEL" : "POINT", &m, &s_pr);
    say("LOGGED");
}

void N3DSDbg_Input(uint32_t held, uint32_t down, int32_t padX, int32_t padY)
{
    int32_t n, i;
    const StereoParam *prm = Stereo_Params(&n);

    if (down & (KEY_B | KEY_START)) {
        s_active = 0;
        fprintf(stderr, "[3dcheck] CLOSE\n");
        fflush(stderr);
        return;
    }
    if (down & KEY_X) {
        s_mode = (s_mode + 1) % M_COUNT;
        s_lineN = 0;
        if (s_mode == M_PIXEL && zoom_now() < 4) s_zoomI = 3;
        say(k_modeName[s_mode]);
    }
    if (down & KEY_Y) { s_numMarks = 0; s_lineN = 0; say("MARKS CLEARED"); }

    if (s_mode == M_TUNE) {
        if (repeat(held, KEY_DUP, 0))   s_tuneSel = (s_tuneSel + n - 1) % n;
        if (repeat(held, KEY_DDOWN, 1)) s_tuneSel = (s_tuneSel + 1) % n;
        {
            int32_t step = prm[s_tuneSel].step * ((held & KEY_L) ? 4 : 1);
            if (repeat(held, KEY_DLEFT, 2))  *prm[s_tuneSel].value -= (int16_t)step;
            if (repeat(held, KEY_DRIGHT, 3)) *prm[s_tuneSel].value += (int16_t)step;
        }
        if (down & KEY_A) {
            if (Stereo_SaveConfig()) { log_params("SAVED"); say("STEREO.INI SAVED"); }
            else say("SAVE FAILED");
        }
        (void)i;
        return;
    }

    /* the cursor: circle pad (fast, slower when zoomed) and D-pad left-right (one pixel) */
    {
        const int32_t z = zoom_now();
        int32_t sx;
        if (padX > -18 && padX < 18) padX = 0;
        if (padY > -18 && padY < 18) padY = 0;
        s_accX += padX;
        s_accY += padY;
        sx = s_accX / (24 * z);  s_accX -= sx * 24 * z;  s_cx += sx;
        sx = s_accY / (24 * z);  s_accY -= sx * 24 * z;  s_cy -= sx;     /* pad up is negative y */
        if (repeat(held, KEY_DLEFT, 2))  s_cx--;
        if (repeat(held, KEY_DRIGHT, 3)) s_cx++;
        clamp_cursor();
    }
    if (repeat(held, KEY_DUP, 0))   { s_want++; s_manual = 1; }
    if (repeat(held, KEY_DDOWN, 1)) { s_want--; s_manual = 1; }
    if (down & KEY_SELECT) s_manual = 0;

    if (down & KEY_R) { if (s_zoomI < 5) s_zoomI++; }
    if (down & KEY_L) { if (s_zoomI > (s_mode == M_PIXEL ? 3 : 0)) s_zoomI--; }

    probe_at(s_cx, s_cy, &s_pr, &s_prOk);
    if (s_prOk && !s_manual) s_want = s_pr.depth;
    if (down & KEY_A) do_mark();
}

/* ---- the top screen ----------------------------------------------------------- */

static inline void plot(uint16_t *fb, int32_t X, int32_t Y, uint16_t c)
{
    if (fb && X >= 0 && X < TOP_W && Y >= 0 && Y < TOP_H)
        fb[X * TOP_H + (TOP_H - 1 - Y)] = c;
}

static void cross(uint16_t *fb, int32_t X, int32_t Y, uint16_t c)
{
    int32_t i;
    for (i = 3; i <= 8; i++) {
        plot(fb, X - i, Y, c); plot(fb, X + i, Y, c);
        plot(fb, X, Y - i, c); plot(fb, X, Y + i, c);
    }
    plot(fb, X, Y, c);
}

static void box(uint16_t *fb, int32_t X0, int32_t Y0, int32_t X1, int32_t Y1, uint16_t c)
{
    int32_t i;
    for (i = X0; i <= X1; i++) { plot(fb, i, Y0, c); plot(fb, i, Y1, c); }
    for (i = Y0; i <= Y1; i++) { plot(fb, X0, i, c); plot(fb, X1, i, c); }
}

/* one marker at output position (px, py) and depth d, in both eyes */
static void marker(uint16_t *L, uint16_t *R, int32_t vx, int32_t vy, int32_t z,
                   int32_t px, int32_t py, int32_t d, uint16_t c, int32_t big)
{
    const int32_t sh = s_strength > 0 ? Stereo_EyeShiftFor(d, s_strength) : 0;
    const int32_t Y = (py - vy) * z + z / 2;
    const int32_t XL = (px + sh - vx) * z + z / 2, XR = (px - sh - vx) * z + z / 2;
    if (big) {
        cross(L, XL, Y, c);
        if (R) cross(R, XR, Y, c);
    } else {
        box(L, XL - 2, Y - 2, XL + 2, Y + 2, c);
        if (R) box(R, XR - 2, Y - 2, XR + 2, Y + 2, c);
    }
}

static void draw_line(uint16_t *L, uint16_t *R, int32_t vx, int32_t vy, int32_t z,
                      const Mark *a, int32_t bx, int32_t by, int32_t bd, uint16_t c)
{
    int32_t n = abs(bx - a->x) > abs(by - a->y) ? abs(bx - a->x) : abs(by - a->y), i;
    if (n < 1) n = 1;
    for (i = 0; i <= n; i += (n > 60 ? 2 : 1)) {
        const int32_t px = a->x + (bx - a->x) * i / n, py = a->y + (by - a->y) * i / n;
        const int32_t d = a->want + (bd - a->want) * i / n;
        const int32_t sh = s_strength > 0 ? Stereo_EyeShiftFor(d, s_strength) : 0;
        const int32_t Y = (py - vy) * z + z / 2;
        plot(L, (px + sh - vx) * z + z / 2, Y, c);
        plot(R, (px - sh - vx) * z + z / 2, Y, c);
    }
}

void N3DSDbg_DrawTop(uint16_t *L, uint16_t *R)
{
    static uint16_t tL[TOP_W * TOP_H], tR[TOP_W * TOP_H];
    const int32_t z = zoom_now();
    int32_t vx = s_cx - TOP_W / (2 * z), vy = s_cy - TOP_H / (2 * z), X, Y, i;

    if (vx < 0) vx = 0;
    if (vx > TOP_W - TOP_W / z) vx = TOP_W - TOP_W / z;
    if (vy < 0) vy = 0;
    if (vy > TOP_H - TOP_H / z) vy = TOP_H - TOP_H / z;

    if (z > 1) {
        memcpy(tL, L, sizeof tL);
        if (R) memcpy(tR, R, sizeof tR);
        for (X = 0; X < TOP_W; X++) {
            const int32_t sx = vx + X / z;
            for (Y = 0; Y < TOP_H; Y++) {
                const int32_t sy = vy + Y / z;
                L[X * TOP_H + (TOP_H - 1 - Y)] = tL[sx * TOP_H + (TOP_H - 1 - sy)];
                if (R) R[X * TOP_H + (TOP_H - 1 - Y)] = tR[sx * TOP_H + (TOP_H - 1 - sy)];
            }
        }
    }
    if (s_mode == M_TUNE)
        return;

    for (i = 0; i < s_numMarks; i++)
        marker(L, R, vx, vy, z, s_marks[i].x, s_marks[i].y, s_marks[i].want, RGB(0, 255, 255), 0);
    if (s_lineN >= 1)
        marker(L, R, vx, vy, z, s_lineA.x, s_lineA.y, s_lineA.want, RGB(255, 0, 255), 0);
    if (s_lineN == 2) {
        marker(L, R, vx, vy, z, s_lineB.x, s_lineB.y, s_lineB.want, RGB(255, 0, 255), 0);
        draw_line(L, R, vx, vy, z, &s_lineA, s_lineB.x, s_lineB.y, s_lineB.want, RGB(255, 0, 255));
    } else if (s_lineN == 1 && s_mode == M_LINE) {
        draw_line(L, R, vx, vy, z, &s_lineA, s_cx, s_cy, s_want, RGB(255, 120, 255));
    }
    /* the cursor: yellow when it follows the surface, orange once you have set its depth */
    marker(L, R, vx, vy, z, s_cx, s_cy, s_want, s_manual ? RGB(255, 140, 0) : RGB(255, 255, 0), 1);
    if (s_mode == M_PIXEL && z >= 3) {
        /* the source pixel under the cursor, outlined at the cursor's depth */
        const int32_t sh = s_strength > 0 ? Stereo_EyeShiftFor(s_want, s_strength) : 0;
        const int32_t Y0 = (s_cy - vy) * z, X0 = (s_cx - vx) * z;
        box(L, X0 + sh * z, Y0, X0 + sh * z + z - 1, Y0 + z - 1, RGB(255, 255, 255));
        if (R) box(R, X0 - sh * z, Y0, X0 - sh * z + z - 1, Y0 + z - 1, RGB(255, 255, 255));
    }
}

/* ---- the bottom screen -------------------------------------------------------- */

static void line(int32_t row, uint16_t c, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void line(int32_t row, uint16_t c, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    N3DSScr_Text(6, 6 + row * 10, buf, 1, c);
}

void N3DSDbg_DrawBottom(void)
{
    const uint16_t white = RGB(235, 235, 235), yel = RGB(255, 230, 90), cyan = RGB(120, 220, 255),
                   grey = RGB(150, 160, 190);
    int32_t r = 0;
    N3DSScr_Fill(0, 0, 320, 240, RGB(14, 18, 30));
    line(r++, yel, "3D CHECKER   MODE: %s   ZOOM %dX", k_modeName[s_mode], (int)zoom_now());
    if (s_strength <= 0)
        line(r++, RGB(250, 120, 100), "THE 3D SLIDER IS OFF: NO DEPTH TO CHECK");
    else
        line(r++, grey, "SLIDER %d OF 256", (int)s_strength);

    if (s_mode == M_TUNE) {
        int32_t n, i, first;
        const StereoParam *p = Stereo_Params(&n);
        first = s_tuneSel > 11 ? s_tuneSel - 11 : 0;
        r++;
        for (i = first; i < n && i < first + 13; i++)
            line(r++, i == s_tuneSel ? yel : white, "%s %-14s %d", i == s_tuneSel ? "+" : " ", p[i].name,
                 (int)*p[i].value);
        r++;
        line(r++, grey, "UP DOWN: CHOOSE   LEFT RIGHT: CHANGE");
        line(r++, grey, "L HELD: FOUR TIMES AS FAST   A: SAVE");
        line(r++, grey, "SEEN LIVE: PITCH, STANDS, HOARDING, HUD");
        line(r++, grey, "SPRITES CHANGE ON THE NEXT FRAME");
    } else {
        r++;
        line(r++, white, "CURSOR %d,%d   SOURCE %d,%d", (int)s_cx, (int)s_cy, (int)src_x(s_cx), (int)src_y(s_cy));
        if (s_prOk) {
            line(r++, white, "WORLD %d,%d", (int)s_pr.wx, (int)s_pr.wy);
            line(r++, cyan, "SURFACE: %s", k_kindName[s_pr.kind]);
            line(r++, white, "DEPTH NOW %d   (EIGHTHS OF A PIXEL)", (int)s_pr.depth);
            line(r++, white, "PALETTE %d   PITCH UNDER IT %d", (int)s_pr.idx, (int)s_pr.bgIdx);
        } else {
            r += 4;
            line(r - 4, grey, "NOTHING RECORDED UNDER THE CURSOR");
        }
        line(r++, s_manual ? RGB(255, 140, 0) : yel, "WANTED DEPTH %d   %s", (int)s_want,
             s_manual ? "(SET BY YOU)" : "(FOLLOWS SURFACE)");
        line(r++, grey, "MARKS %d", (int)s_numMarks);
        r++;
        line(r++, grey, "MOVE: CIRCLE PAD, D-PAD LEFT RIGHT");
        line(r++, grey, "DEPTH: D-PAD UP DOWN   SELECT: RESET");
        line(r++, grey, "L R: ZOOM    A: MARK    Y: CLEAR");
        line(r++, grey, "X: MODE (POINT PIXEL LINE TUNE)");
        line(r++, grey, "B OR START: BACK TO THE MATCH");
        if (s_mode == M_LINE)
            line(r++, grey, "%s", s_lineN == 1 ? "A: SECOND END" : "A: FIRST END");
    }
    if (s_msgTicks > 0) {
        s_msgTicks--;
        N3DSScr_Text(6, 228, s_msg, 1, RGB(255, 255, 255));
    }
}

#endif /* __3DS__ */
