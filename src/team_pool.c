/*
 * team_pool.c -- the off-file pool below a world's lowest division (include/team_pool.h).
 *
 * The SWOS 96/97 Mod's design (tools/gplay.c ri_season / ri_apply / ri_cache, MIT, Davide
 * Lorigliola; notes/world-2025-26.md "Off-file Eccellenza pool"), written for our engine:
 * the queue starts in the pack's first order; at a season end each slot relegated out of
 * the lowest division takes the queue's head in turn, the identities going down join its
 * end, the taken leave its front (its length stays the pool's); the manager's club is
 * never swapped. A slot's record from its identity: everything but the slot's own bytes --
 * country, ordinal, global number, controls (0..4), its flags (23) and its division (25).
 */
#include "team_pool.h"
#include "league_groups.h"
#include "game_types.h"
#include "play_match_menu.h"
#include "file_io.h"
#include "swos_io.h"

extern uint16_t _gameType;
extern uint16_t _numSelectedTeams;
extern uint16_t _careerFileBuffer;              /* the TMD pool's count */
extern uint8_t  _tmdFileBuffer[];               /* its records */
extern uint16_t _teamsLoaded;
extern TeamFileEx *Career_ParkedTeams(uint16_t *count);

#define REC 684

TeamPoolState g_teamPool;

static uint8_t *s_orig;                         /* the pooled file as shipped: count + records */
static uint8_t *s_pool;                         /* the pool's file */
static uint8_t  s_recordsOf = 0xFF;             /* (whose file s_orig holds) */
static uint8_t  s_swapped[TEAM_POOL_SLOTS / 8]; /* this season end's swapped slots */
static uint16_t s_out[TEAM_POOL_QUEUE];         /* the identities that went down, in order */
static int32_t  s_taken, s_outs;

static const TeamPool *pool_for(uint8_t country)
{
    int32_t n = 0;
    const TeamPool *tp = World_TeamPools(&n);
    for (int32_t i = 0; i < n; i++)
        if (tp[i].country == country)
            return &tp[i];
    return 0;
}

void TeamPool_Reset(void)
{
    uint8_t *p = (uint8_t *)&g_teamPool;
    for (uint32_t i = 0; i < sizeof g_teamPool; i++)
        p[i] = 0;
}

static void start(const TeamPool *tp)
{
    if (g_teamPool.init && g_teamPool.country == tp->country)
        return;
    TeamPool_Reset();
    g_teamPool.country = tp->country;
    g_teamPool.queued = tp->count < TEAM_POOL_QUEUE ? tp->count : TEAM_POOL_QUEUE;
    for (int32_t i = 0; i < g_teamPool.queued; i++)
        g_teamPool.queue[i] = (uint16_t)(TEAM_POOL_SLOTS + i);
    g_teamPool.init = 1;
}

/* ---- the identities' records ---- */

#ifdef SWOS_SDL_BUILD
extern int DataJson_LoadTeamFileForCountry(int countryNum, unsigned char *buf, int maxTeams);
extern int DataJson_LoadWorldTeamFile(const char *name, unsigned char *buf, int maxTeams);
#endif

static int32_t load_records(const TeamPool *tp)
{
    if (s_recordsOf == tp->country)
        return 1;
    if (!s_orig)
        s_orig = (uint8_t *)swos_io_alloc(2 + TEAM_POOL_SLOTS * REC);
    if (!s_pool)
        s_pool = (uint8_t *)swos_io_alloc(2 + TEAM_POOL_QUEUE * REC);
    if (!s_orig || !s_pool)
        return 0;
#ifdef SWOS_SDL_BUILD
    if (DataJson_LoadTeamFileForCountry(tp->country, s_orig, TEAM_POOL_SLOTS) <= 0 ||
        DataJson_LoadWorldTeamFile(tp->json, s_pool, TEAM_POOL_QUEUE) <= 0)
        return 0;
#else
    {
        uint8_t name[] = "data/TEAM.000";
        uint32_t size = 0;
        name[10] = (uint8_t)('0' + tp->country / 100);
        name[11] = (uint8_t)('0' + tp->country / 10 % 10);
        name[12] = (uint8_t)('0' + tp->country % 10);
        if (LoadFileEx(name, s_orig, &size) != 0)
            return 0;
        name[10] = (uint8_t)('0' + tp->file / 100);
        name[11] = (uint8_t)('0' + tp->file / 10 % 10);
        name[12] = (uint8_t)('0' + tp->file % 10);
        if (LoadFileEx(name, s_pool, &size) != 0)
            return 0;
    }
#endif
    s_recordsOf = tp->country;
    return 1;
}

static void copy_identity(uint8_t *dst, uint16_t id)
{
    const uint8_t *src;
    if (id < TEAM_POOL_SLOTS) {
        if (id >= *(uint16_t *)s_orig)
            return;
        src = s_orig + 2 + (uint32_t)id * REC;
    } else {
        if (id - TEAM_POOL_SLOTS >= *(uint16_t *)s_pool)
            return;
        src = s_pool + 2 + (uint32_t)(id - TEAM_POOL_SLOTS) * REC;
    }
    for (int32_t b = 0; b < REC; b++)
        if (b > 4 && b != 23 && b != 25)
            dst[b] = src[b];
}

void TeamPool_ApplyToFile(uint8_t country, uint8_t *buf)
{
    const TeamPool *tp;
    if (_gameType != GAME_TYPE_CAREER || !(tp = pool_for(country)))
        return;
    start(tp);
    uint16_t n = *(uint16_t *)buf, any = 0;
    for (uint16_t i = 0; i < n && i < TEAM_POOL_SLOTS; i++)
        any |= g_teamPool.map[i];
    if (!any || !load_records(tp))
        return;
    for (uint16_t i = 0; i < n && i < TEAM_POOL_SLOTS; i++)
        if (g_teamPool.map[i])
            copy_identity(buf + 2 + (uint32_t)i * REC, (uint16_t)(g_teamPool.map[i] - 1));
}

/* ---- the season end ---- */

int32_t TeamPool_Swap(uint8_t country, uint8_t ordinal)
{
    const TeamPool *tp = pool_for(country);
    if (!tp)
        return -1;
    start(tp);
    if (s_taken + s_outs == 0)
        for (int32_t i = 0; i < TEAM_POOL_SLOTS / 8; i++)
            s_swapped[i] = 0;
    if (s_taken >= g_teamPool.queued || s_outs >= TEAM_POOL_QUEUE)
        return -1;
    const uint16_t was = g_teamPool.map[ordinal] ? (uint16_t)(g_teamPool.map[ordinal] - 1) : ordinal;
    const uint16_t in = g_teamPool.queue[s_taken++];
    s_out[s_outs++] = was;
    g_teamPool.map[ordinal] = in == ordinal ? 0 : (uint16_t)(in + 1);
    s_swapped[ordinal >> 3] |= (uint8_t)(1 << (ordinal & 7));
    return in;
}

static void refresh(TeamFileEx *recs, uint32_t n, const TeamPool *tp)
{
    for (uint32_t k = 0; k < n; k++) {
        TeamFile *t = &recs[k].team;
        const uint8_t o = t->teamOrdinal;
        if (t->countryNumber != tp->country || !(s_swapped[o >> 3] & (1 << (o & 7))))
            continue;
        copy_identity((uint8_t *)&recs[k], g_teamPool.map[o] ? (uint16_t)(g_teamPool.map[o] - 1) : o);
    }
}

void TeamPool_SwapsDone(void)
{
    const TeamPool *tp = g_teamPool.init ? pool_for(g_teamPool.country) : 0;
    if (!tp || (!s_taken && !s_outs))
        return;
    /* The queue: drop the taken, append the ones that went down (its length stays). */
    int32_t k = 0;
    const int32_t n = g_teamPool.queued;
    for (int32_t i = s_taken; i < n; i++)
        g_teamPool.queue[k++] = g_teamPool.queue[i];
    for (int32_t i = 0; i < s_outs && k < n; i++)
        g_teamPool.queue[k++] = s_out[i];
    g_teamPool.queued = (uint8_t)k;
    s_taken = s_outs = 0;

    if (load_records(tp)) {
        uint16_t parked = 0;
        TeamFileEx *p = Career_ParkedTeams(&parked);
        refresh(_selectedTeams, _numSelectedTeams, tp);
        refresh((TeamFileEx *)_tmdFileBuffer, _careerFileBuffer, tp);
        refresh(p, parked, tp);
    }
    _teamsLoaded = 0;               /* (the file in the buffer: read again, identities applied) */
}

uint8_t TeamPool_Area(uint8_t country, uint8_t ordinal, int32_t p, uint8_t own)
{
    const TeamPool *tp;
    if (!g_teamPool.init || g_teamPool.country != country || !g_teamPool.map[ordinal] ||
        p < 0 || p > 1 || !(tp = pool_for(country)))
        return own;
    const uint16_t id = (uint16_t)(g_teamPool.map[ordinal] - 1);
    if (id >= TEAM_POOL_SLOTS)
        return id - TEAM_POOL_SLOTS < tp->count ? tp->area[id - TEAM_POOL_SLOTS][p] : own;
    /* another slot's own identity: that slot's area */
    const LeagueGroups *lg = LeagueGroups_ForCountry(country);
    for (uint16_t i = 0; lg && i < lg->clubs; i++)
        if (lg->club[i].ordinal == id)
            return lg->club[i].area[p];
    return own;
}
