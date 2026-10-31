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
#include <dirent.h>
#include <strings.h>
#include "n3ds.h"
#include "stereo.h"
#include "display.h"
#include "swosdata.h"
#include "adf.h"
#include "sd_manifest.h"
#include "miniz_swos.h"
#include <errno.h>

/* The data setup makes a folder path one prefix at a time ("sdmc:", "sdmc:/3ds", ...), and
 * libctru's mkdir refuses the bare device name -- "cannot create the folder .../assets/data".
 * Linked with -Wl,--wrap=mkdir: a device name alone already exists. */
int __real_mkdir(const char *path, mode_t mode);
int __wrap_mkdir(const char *path, mode_t mode)
{
    size_t n = path ? strlen(path) : 0;
    if (n && path[n - 1] == ':' && strchr(path, '/') == NULL) {
        errno = EEXIST;
        return -1;
    }
    return __real_mkdir(path, mode);
}

/* The game uses the stack like a desktop program does. */
u32 __stacksize__ = 1024 * 1024;

#define TOP_W   400
#define TOP_H   240

static int32_t  s_videoUp;
static int32_t  s_3dOn = -1;
static int32_t  s_oldModel, s_presentTick;
static uint16_t s_pal565[256];
static uint16_t s_xmap[TOP_W];
static int32_t  s_xmapFor;
static uint8_t  s_lineL[FRONTBUFFER_MAX_STRIDE_BYTES];
static uint8_t  s_lineR[FRONTBUFFER_MAX_STRIDE_BYTES];
static PrintConsole s_console;

/* ---- start-up -------------------------------------------------------------- */

void N3DS_EarlyInit(void)
{
    {
        bool isNew = false;
        osSetSpeedupEnable(true);       /* New 3DS: 804 MHz and the L2 cache */
        APT_CheckNew3DS(&isNew);
        s_oldModel = !isNew;            /* 268 MHz: show every other frame, see N3DS_Present */
    }
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

/* ---- seasons ---------------------------------------------------------------
 *
 * One season is a set of the game's Amiga files: the two ADFs, a hard disk image
 * (.hdf) or a WHDLoad .lha, alone or zipped. The ones in the main folder are one
 * season (the converter takes the best of them, 96/97 first); every folder under
 * seasons/ is another -- e.g. seasons/2526/ for sensiblesoccer.de's 25/26 when its
 * Amiga version is out. The first start, or holding L while the game starts, lists
 * them all to choose from; the choice is converted into assets/ and remembered in
 * season.txt. */

#define N3DS_SEASONS_DIR  N3DS_USER_DIR "/seasons"
#define N3DS_MAX_SEASONS  12
#define N3DS_PENDING_FILE N3DS_USER_DIR "/season_pending.txt"

typedef struct {
    char       label[40];       /* the folder: "MAIN FOLDER" or seasons/<name> */
    const char *season;          /* what the converter says it is: "96/97", "24/25", ... */
    char       title[40];       /* what the list calls it: "CLASSIC SWOS 96/97", "SWOS 25/26" */
    SdSources  src;
} N3dsSeason;

/* "96/97" is the classic; a season the converter knows is "SWOS yy/yy"; one it does not
 * (a new release) is named from its folder when the folder looks like a season --
 * 2526, 25-26, 25_26, SWOS2526 -- else by the folder itself. */
static void season_title(N3dsSeason *se)
{
    const char *f = se->label;
    int32_t i, n = 0;
    char d[4];
    if (strcmp(se->season, "96/97") == 0) {
        snprintf(se->title, sizeof se->title, "CLASSIC SWOS 96/97");
        return;
    }
    if (strlen(se->season) == 5 && se->season[2] == '/') {
        snprintf(se->title, sizeof se->title, "SWOS %s", se->season);
        return;
    }
    for (i = 0; f[i] && n < 4; i++) {
        if (f[i] >= '0' && f[i] <= '9') d[n++] = f[i];
        else if (n && f[i] != '-' && f[i] != '_' && f[i] != '/' && f[i] != ' ') break;
    }
    if (n == 4)
        snprintf(se->title, sizeof se->title, "SWOS %c%c/%c%c", d[0], d[1], d[2], d[3]);
    else
        snprintf(se->title, sizeof se->title, "%.30s", f);
    for (i = 0; se->title[i]; i++)
        if (se->title[i] >= 'a' && se->title[i] <= 'z') se->title[i] = (char)(se->title[i] - 32);
}

static N3dsSeason s_seasons[N3DS_MAX_SEASONS];
static int32_t    s_numSeasons;

static int32_t has_ext(const char *name, const char *const *exts)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return 0;
    for (; *exts; exts++)
        if (strcasecmp(dot + 1, *exts) == 0)
            return 1;
    return 0;
}

static void add_season(const char *label, SdSources *src)
{
    N3dsSeason *se;
    if (!SdSources_HaveAmiga(src) || s_numSeasons >= N3DS_MAX_SEASONS)
        return;
    se = &s_seasons[s_numSeasons++];
    memset(se, 0, sizeof *se);
    snprintf(se->label, sizeof se->label, "%s", label);
    se->src = *src;
    se->season = SdSources_AmigaSeason(&se->src);
    if (!se->season) se->season = "?";
    season_title(se);
}

/* ---- seasons that are only team data ------------------------------------------
 *
 * A season folder without a whole game in it -- loose TEAM.000 ... files, a .zip of
 * them, or an .adf data disk -- is played over the full game found elsewhere (the
 * 96/97 disks first): its files are gathered into season_data/<folder>/ and the
 * converter reads them in place of the game's own (SdSources.overDir). */

#define N3DS_SEASON_DATA  N3DS_USER_DIR "/season_data"

static const char *base_name(const char *p)
{
    const char *a = strrchr(p, '/'), *b = strrchr(p, ':');
    if (b && (!a || b > a)) a = b;
    return a ? a + 1 : p;
}

/* Is `name` one of the game's data/ files (TEAM.000, POOLPLYR.DAT, *.TMD ...)? */
static int32_t team_file(const char *name)
{
    int32_t i;
    for (i = 0; g_sdAmigaCopy[i]; i++)
        if (strncmp(g_sdAmigaCopy[i], "data/", 5) == 0 && strcasecmp(g_sdAmigaCopy[i] + 5, name) == 0)
            return 1;
    return 0;
}

static int32_t save_file(const char *dir, const char *name, const void *data, uint32_t n)
{
    char path[700], up[64];
    int32_t i;
    FILE *f;
    for (i = 0; name[i] && i < 63; i++)
        up[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
    up[i] = 0;
    snprintf(path, sizeof path, "%s/%s", dir, up);
    if (!(f = fopen(path, "wb"))) return 0;
    i = fwrite(data, 1, n, f) == n;
    fclose(f);
    return i;
}

typedef struct { Adf *adf; const char *dir; int32_t count; } AdfGather;

static int32_t gather_adf_cb(const char *path, uint32_t size, void *user)
{
    AdfGather *g = (AdfGather *)user;
    char err[256];
    uint32_t n;
    uint8_t *d;
    (void)size;
    if (!team_file(base_name(path))) return 0;
    d = Adf_ReadFile(g->adf, path, &n, err);
    if (d) {
        g->count += save_file(g->dir, base_name(path), d, n);
        free(d);
    }
    return 0;
}

static int32_t gather_zip(const char *path, const char *dir)
{
    mz_zip_archive z;
    int32_t i, count = 0;
    memset(&z, 0, sizeof z);
    if (!mz_zip_reader_init_file(&z, path, 0)) return 0;
    for (i = 0; i < (int32_t)mz_zip_reader_get_num_files(&z); i++) {
        mz_zip_archive_file_stat st;
        char out[700];
        if (!mz_zip_reader_file_stat(&z, (mz_uint)i, &st) || st.m_is_directory) continue;
        if (!team_file(base_name(st.m_filename))) continue;
        {
            size_t n;
            void *d = mz_zip_reader_extract_to_heap(&z, (mz_uint)i, &n, 0);
            if (d) {
                count += save_file(dir, base_name(st.m_filename), d, (uint32_t)n);
                mz_free(d);
            }
        }
        (void)out;
    }
    mz_zip_reader_end(&z);
    return count;
}

/* Everything in `src` (and its folders, two deep) that is team data, into `dir`. */
static int32_t gather_team_data(const char *src, const char *dir, int32_t depth)
{
    static const char *const archives[] = { "zip", NULL };
    static const char *const disks[] = { "adf", "adz", NULL };
    DIR *d = opendir(src);
    struct dirent *e;
    int32_t count = 0;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        char path[700], err[256];
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", src, e->d_name);
        if (e->d_type == DT_DIR) {
            if (depth < 2) count += gather_team_data(path, dir, depth + 1);
        } else if (team_file(e->d_name)) {
            SgBuf b;
            if (Sg_ReadFile(path, &b, err) == 0) {
                count += save_file(dir, e->d_name, b.data, b.size);
                Sg_BufFree(&b);
            }
        } else if (has_ext(e->d_name, archives)) {
            count += gather_zip(path, dir);
        } else if (has_ext(e->d_name, disks)) {
            AdfGather g = { Adf_Open(path, err), dir, 0 };
            if (g.adf) {
                Adf_Walk(g.adf, gather_adf_cb, &g);
                Adf_Close(g.adf);
                count += g.count;
            }
        }
    }
    closedir(d);
    return count;
}

static void clear_dir(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char path[700];
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || e->d_type == DT_DIR) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        remove(path);
    }
    closedir(d);
}

static void find_seasons(void)
{
    static const char *const exts[] = { "adf", "adz", "hdf", "lha", "zip", NULL };
    static SdSources src;
    DIR *d;
    struct dirent *e;
    char path[600];

    s_numSeasons = 0;
    /* the main folder's own files, not its subfolders */
    memset(&src, 0, sizeof src);
    if ((d = opendir(N3DS_USER_DIR)) != NULL) {
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.' || !has_ext(e->d_name, exts)) continue;
            snprintf(path, sizeof path, "%s/%s", N3DS_USER_DIR, e->d_name);
            SdSources_AddFile(&src, path);
        }
        closedir(d);
    }
    add_season("MAIN FOLDER", &src);
    /* every folder under seasons/: a whole game, or only its team data */
    mkdir(N3DS_SEASONS_DIR, 0777);
    mkdir(N3DS_SEASON_DATA, 0777);
    {
        static char dataOnly[N3DS_MAX_SEASONS][40];
        int32_t numData = 0, i, base = -1;
        if ((d = opendir(N3DS_SEASONS_DIR)) != NULL) {
            while ((e = readdir(d)) != NULL && s_numSeasons < N3DS_MAX_SEASONS) {
                char label[40];
                if (e->d_name[0] == '.' || e->d_type != DT_DIR) continue;
                snprintf(path, sizeof path, "%s/%s", N3DS_SEASONS_DIR, e->d_name);
                snprintf(label, sizeof label, "%.30s", e->d_name);
                memset(&src, 0, sizeof src);
                SdSources_Scan(&src, path);
                if (SdSources_HaveAmiga(&src))
                    add_season(label, &src);
                else if (numData < N3DS_MAX_SEASONS)
                    snprintf(dataOnly[numData++], sizeof dataOnly[0], "%s", label);
            }
            closedir(d);
        }
        /* The game the team-data seasons are played over: the 96/97 disks if they are
         * here, else the first whole game found. */
        for (i = 0; i < s_numSeasons; i++)
            if (base < 0 || strcmp(s_seasons[i].season, "96/97") == 0)
                if (base < 0 || strcmp(s_seasons[base].season, "96/97") != 0) base = i;
        for (i = 0; i < numData && base >= 0; i++) {
            char from[700], cache[700];
            snprintf(from, sizeof from, "%s/%s", N3DS_SEASONS_DIR, dataOnly[i]);
            snprintf(cache, sizeof cache, "%s/%s", N3DS_SEASON_DATA, dataOnly[i]);
            mkdir(cache, 0777);
            clear_dir(cache);
            if (gather_team_data(from, cache, 0) < 10)
                continue;                        /* not a season's team data */
            src = s_seasons[base].src;
            snprintf(src.overDir, sizeof src.overDir, "%s", cache);
            add_season(dataOnly[i], &src);
        }
    }
}

static void remember_season(const N3dsSeason *se)
{
    FILE *f = fopen(N3DS_USER_DIR "/season.txt", "w");
    if (!f) return;
    fprintf(f, "%s\n%s\n", se->label, se->season);
    fclose(f);
}

static void read_remembered(char *label, int32_t cap)
{
    FILE *f = fopen(N3DS_USER_DIR "/season.txt", "r");
    label[0] = 0;
    if (!f) return;
    if (fgets(label, cap, f))
        label[strcspn(label, "\r\n")] = 0;
    fclose(f);
}

/* The list on the bottom screen: up/down and A, or a tap on a line. */
static int32_t pick_season(const char *current)
{
    int32_t sel = 0, i, drawn = -1;
    for (i = 0; i < s_numSeasons; i++)
        if (strcmp(s_seasons[i].label, current) == 0) sel = i;
    while (aptMainLoop()) {
        u32 down;
        hidScanInput();
        down = hidKeysDown();
        if (down & KEY_DUP)   sel = (sel + s_numSeasons - 1) % s_numSeasons;
        if (down & KEY_DDOWN) sel = (sel + 1) % s_numSeasons;
        if (down & KEY_A)     return sel;
        if (down & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            i = (tp.py / 8 - 4) / 2;          /* the list starts on row 5, two rows a line */
            if (i >= 0 && i < s_numSeasons) return i;
        }
        if (drawn != sel) {
            consoleClear();
            printf("\x1b[1;1H\x1b[32mChoose a season\x1b[0m\n"
                   "Tap one, or D-Pad and A.\n\n\n");
            for (i = 0; i < s_numSeasons; i++)
                printf(" %s%-22.22s\x1b[0m\n   \x1b[90m%.30s\x1b[0m\n", i == sel ? "\x1b[33m> " : "  ",
                       s_seasons[i].title, s_seasons[i].label);
            printf("\nAdd seasons as folders in\n sdmc:/3ds/OpenSWOS3D/seasons/\n"
                   "Hold L while starting to\n choose again.\n"
                   "Careers belong to the season\n they were started in.\n");
            drawn = sel;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    N3DS_Exit();
    exit(0);
}

void N3DS_PrepareData(const char *assetsDir)
{
    static SdJob job;
    char ini[1100], err[256], current[64], pending[64];
    int32_t choose, have, pick, i;

    hidScanInput();
    choose = (hidKeysHeld() & KEY_L) != 0;
    snprintf(ini, sizeof ini, "%s/ASSETS.ini", assetsDir);
    have = file_exists(ini);
    pending[0] = 0;
    {
        /* chosen from the menu's SEASON button: the game restarted to make it */
        FILE *f = fopen(N3DS_PENDING_FILE, "r");
        if (f) {
            if (fgets(pending, sizeof pending, f))
                pending[strcspn(pending, "\r\n")] = 0;
            fclose(f);
            remove(N3DS_PENDING_FILE);
        }
    }
    if (have && !choose && !pending[0])
        return;                                     /* made already */

    printf("Looking for your SWOS disks...\n");
    find_seasons();
    if (s_numSeasons == 0) {
        if (have)
            return;                                 /* L held, nothing new: keep it */
        printf("\x1b[31mNo SWOS 96/97 disks found.\x1b[0m\n\n"
               "Copy your two Amiga disk images\n"
               "(.adf, or a .zip/.lha/.hdf of the\n"
               "game) into:\n\n"
               "  sdmc:/3ds/OpenSWOS3D/\n\n"
               "or one folder per season into\n"
               "  sdmc:/3ds/OpenSWOS3D/seasons/\n\n"
               "then start OpenSWOS 3D again.\n");
        if (SdSources_AmigaProblem()[0])
            printf("\nNote: %.200s\n", SdSources_AmigaProblem());
        wait_and_quit();
    }
    read_remembered(current, sizeof current);
    pick = -1;
    for (i = 0; pending[0] && i < s_numSeasons; i++)
        if (strcmp(s_seasons[i].label, pending) == 0) pick = i;
    if (pick < 0)
        pick = s_numSeasons == 1 && !choose ? 0 : pick_season(current);
    if (have && strcmp(current, s_seasons[pick].label) == 0) {
        consoleClear();
        return;                                     /* the one already made */
    }

    consoleClear();
    printf("\x1b[1;1HMaking the game data for\n%s\n(%s).\n\n",
           s_seasons[pick].title, s_seasons[pick].label);
    memset(&job, 0, sizeof job);
    job.src = s_seasons[pick].src;
    snprintf(job.outDir, sizeof job.outDir, "%s", assetsDir);
    snprintf(job.resDir, sizeof job.resDir, "romfs:");
    job.log = on_setup_log;
    job.progress = on_setup_progress;
    remove(ini);                                    /* half-made is not made */
    if (SwosData_Run(&job, err) != 0) {
        printf("\n\x1b[31mThe data setup failed:\x1b[0m\n%.300s\n", err);
        wait_and_quit();
    }
    remember_season(&s_seasons[pick]);
    printf("\nDone.\n");
}

/* ---- picture --------------------------------------------------------------- */

void N3DS_VideoReady(void)
{
    /* The text console has done its job (the first-run setup): from here the bottom
     * screen is drawn directly (n3ds_screens.c), so stdout must stop drawing on it. */
    fflush(stdout);
    if (!freopen(N3DS_USER_DIR "/stdout.txt", "w", stdout))
        fclose(stdout);
    else
        setvbuf(stdout, NULL, _IOFBF, 8192);
    N3DSScr_Init();
}

static void set_3d(int32_t on)
{
    if (on == s_3dOn) return;
    s_3dOn = on;
    gfxSet3D(on ? true : false);
}

static int32_t main_menu_showing(void);

void N3DS_Present(const uint8_t *fb, int32_t w, int32_t h, const uint8_t *rgba)
{
    uint16_t *outL, *outR;
    int32_t x, y, strength, depth, kind;

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
    Stereo_FrameHasDepth(fb);               /* match, menu, or neither */
    kind = Stereo_FrameKind();
    depth = strength > 0 && kind == STEREO_FRAME_MATCH;
    set_3d(strength > 0);

    /* Outside a match an original 3DS shows every other frame (a menu does not need 60);
     * in a match every frame is shown. And a log line every 5 s says what it really got. */
    {
        static uint64_t t0; static int32_t shown;
        uint64_t now = osGetTime();
        if (s_oldModel && kind != STEREO_FRAME_MATCH && (s_presentTick++ & 1))
            return;
        if (!t0) t0 = now;
        shown++;
        if (now - t0 >= 5000) {
            fprintf(stderr, "[fps] %d.%d shown/s (%s, %s)\n", (int)(shown * 1000 / (now - t0)),
                    (int)(shown * 10000 / (now - t0) % 10), kind == STEREO_FRAME_MATCH ? "match" : "menu",
                    s_oldModel ? "old 3DS" : "new 3DS");
            t0 = now; shown = 0;
        }
    }

    outL = (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
    outR = s_3dOn ? (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL) : NULL;

    /* The bottom screen: the menu to tap, the match's score and radar, or the
     * controls and credits. */
    if (kind == STEREO_FRAME_MENU) {
        extern int32_t N3DS_OverlayActive(void);
        if (N3DS_OverlayActive())
            N3DSScr_InvalidateMenu();
        else
            N3DSScr_BottomMenu(fb, w, s_pal565, main_menu_showing());
    }
    else if (kind == STEREO_FRAME_MATCH)
        N3DSScr_BottomMatch(s_pal565);
    else
        N3DSScr_BottomInfo();

    /* In menus the top screen shows the 3D title scene; the menu is on the bottom. */
    if (kind == STEREO_FRAME_MENU) {
        N3DSScr_TitleTop(outL, outR, strength);
        gfxFlushBuffers();
        gfxSwapBuffers();
        return;
    }

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
            l = r = fb + sy * w;    /* flat, on the screen */
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

static int32_t s_tapPending, s_tapX, s_tapY;

/* ---- the season list, from the main menu's SEASON button ------------------- */

#define OV_ROW_Y0   34
#define OV_ROW_H    22
#define OV_BTN_Y0   206
#define OV_BTN_Y1   234
#define C565(r, g, b)  ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

enum { OV_NONE, OV_LIST, OV_CONFIRM, OV_SWITCH };
static int32_t s_overlay, s_overlayPick;
static char    s_overlayCurrent[64];

static int32_t main_menu_showing(void)
{
    extern volatile uintptr_t _currentMenuPtr;
    extern uint8_t _swosMainMenu[];
    return _currentMenuPtr == (uintptr_t)_swosMainMenu;
}

static void button(int32_t x0, int32_t x1, const char *t, uint16_t bg, uint16_t fg)
{
    N3DSScr_Fill(x0, OV_BTN_Y0, x1, OV_BTN_Y1, bg);
    N3DSScr_Text(x0 + (x1 - x0 - N3DSScr_TextWidth(t, 2)) / 2, OV_BTN_Y0 + 7, t, 2, fg);
}

static void overlay_draw(void)
{
    const uint16_t ink = C565(235, 235, 235), back = C565(14, 18, 30);
    char line[80];
    int32_t i;
    N3DSScr_Fill(0, 0, 320, 240, back);
    if (s_overlay == OV_LIST) {
        N3DSScr_Text(8, 8, "CHOOSE A SEASON", 2, C565(255, 230, 90));
        for (i = 0; i < s_numSeasons && OV_ROW_Y0 + (i + 1) * OV_ROW_H <= OV_BTN_Y0; i++) {
            int32_t y = OV_ROW_Y0 + i * OV_ROW_H, cur = strcmp(s_seasons[i].label, s_overlayCurrent) == 0;
            N3DSScr_Fill(6, y, 314, y + OV_ROW_H - 3, cur ? C565(40, 110, 50) : C565(36, 42, 64));
            N3DSScr_Text(12, y + 3, s_seasons[i].title, 2, ink);
            snprintf(line, sizeof line, "%s%.20s", cur ? "PLAYING  " : "", s_seasons[i].label);
            N3DSScr_Text(310 - N3DSScr_TextWidth(line, 1), y + 7, line, 1,
                         cur ? C565(250, 220, 90) : C565(150, 160, 190));
        }
        if (s_numSeasons == 0)
            N3DSScr_Text(12, OV_ROW_Y0 + 6, "NO SEASONS FOUND", 1, ink);
        N3DSScr_Text(8, OV_BTN_Y0 - 12, "ADD SEASONS AS FOLDERS IN 3DS/OPENSWOS3D/SEASONS", 1, C565(150, 160, 190));
        button(8, 312, "BACK", C565(70, 76, 100), ink);
    } else if (s_overlay == OV_CONFIRM) {
        const N3dsSeason *se = &s_seasons[s_overlayPick];
        N3DSScr_Text(8, 8, "SWITCH SEASON?", 2, C565(255, 230, 90));
        N3DSScr_Text(12, 36, se->title, 2, ink);
        N3DSScr_Text(12, 62, "THE GAME WILL RESTART AND MAKE", 1, ink);
        N3DSScr_Text(12, 74, "ITS DATA FROM THIS SEASON.", 1, ink);
        N3DSScr_Text(12, 86, "IT TAKES ABOUT A MINUTE.", 1, ink);
        N3DSScr_Text(12, 108, "CAREERS AND SAVED COMPETITIONS", 1, C565(250, 160, 120));
        N3DSScr_Text(12, 120, "BELONG TO THE SEASON THEY WERE", 1, C565(250, 160, 120));
        N3DSScr_Text(12, 132, "STARTED IN.", 1, C565(250, 160, 120));
        button(8, 156, "SWITCH", C565(40, 130, 60), ink);
        button(164, 312, "CANCEL", C565(70, 76, 100), ink);
    } else if (s_overlay == OV_SWITCH) {
        N3DSScr_Text(8, 100, "RESTARTING...", 2, C565(255, 230, 90));
    }
}

/* 1 when the overlay is up (the bottom screen is the overlay's, not the menu's). */
int32_t N3DS_OverlayActive(void)
{
    if (s_overlay != OV_NONE)
        overlay_draw();
    return s_overlay != OV_NONE;
}

/* A tap while the bottom screen shows the menu. 0: the menu's to have. */
static int32_t overlay_tap(int32_t tx, int32_t ty)
{
    if (s_overlay == OV_NONE) {
        if (!main_menu_showing() || tx < N3DS_TAB_X0 || tx >= N3DS_TAB_X1 ||
            ty < N3DS_TAB_Y0 || ty >= N3DS_TAB_Y1)
            return 0;
        N3DSScr_Fill(60, 100, 260, 130, C565(14, 18, 30));
        N3DSScr_Text(80, 111, "LOOKING FOR SEASONS...", 1, C565(235, 235, 235));
        gfxFlushBuffers();
        find_seasons();
        read_remembered(s_overlayCurrent, sizeof s_overlayCurrent);
        s_overlay = OV_LIST;
        return 1;
    }
    if (s_overlay == OV_LIST) {
        int32_t i = (ty - OV_ROW_Y0) / OV_ROW_H;
        if (ty >= OV_BTN_Y0) {
            s_overlay = OV_NONE;
        } else if (ty >= OV_ROW_Y0 && i < s_numSeasons) {
            if (strcmp(s_seasons[i].label, s_overlayCurrent) == 0)
                s_overlay = OV_NONE;              /* the one playing now */
            else {
                s_overlayPick = i;
                s_overlay = OV_CONFIRM;
            }
        }
        return 1;
    }
    if (s_overlay == OV_CONFIRM && ty >= OV_BTN_Y0) {
        if (tx < 160) {
            FILE *f = fopen(N3DS_PENDING_FILE, "w");
            if (f) {
                fprintf(f, "%s\n", s_seasons[s_overlayPick].label);
                fclose(f);
                s_overlay = OV_SWITCH;
                overlay_draw();
                gfxFlushBuffers();
                aptSetChainloaderToSelf();    /* start again when this run ends */
                return 2;
            }
        }
        s_overlay = OV_LIST;
    }
    return 1;
}

int32_t N3DS_TakeMenuTap(int32_t *x, int32_t *y)
{
    if (!s_tapPending)
        return 0;
    s_tapPending = 0;
    *x = s_tapX;
    *y = s_tapY;
    return 1;
}

int32_t N3DS_PollInput(void)
{
    u32 held, down;
    int32_t dirs = 0;

    if (!aptMainLoop())
        return 0;                       /* HOME menu close / power: quit */

    hidScanInput();
    held = hidKeysHeld();
    down = hidKeysDown();

    /* A tap on the menu (bottom screen): the SEASON button and its list first, else
     * the menu's, taken by MenuProc (MenuTapAt). */
    if ((down & KEY_TOUCH) && N3DSScr_BottomMode() == N3DS_BOTTOM_MENU) {
        touchPosition tp;
        int32_t r;
        hidTouchRead(&tp);
        r = overlay_tap(tp.px, tp.py);
        if (r == 2)
            return 0;                             /* quit; the system starts us again */
        if (r == 0) {
            N3DSScr_TouchToMenu(tp.px, tp.py, &s_tapX, &s_tapY);
            s_tapPending = 1;
        }
    }
    if (s_overlay != OV_NONE) {
        if (N3DSScr_BottomMode() != N3DS_BOTTOM_MENU)
            s_overlay = OV_NONE;                  /* the menu went away */
        InputSDL_TouchControls(0, 0);             /* the menu waits while the list is up */
        InputSDL_TouchSkip(0);
        return 1;
    }

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
